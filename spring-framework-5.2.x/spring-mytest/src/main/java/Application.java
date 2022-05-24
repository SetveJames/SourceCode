import org.service.AService;
import org.service.BService;
import org.springframework.context.annotation.AnnotationConfigApplicationContext;

/**
 * 启动类
 */
public class Application {

	public static void main(String[] args) {
		AnnotationConfigApplicationContext context = new AnnotationConfigApplicationContext(AppConfig.class);
		AService bean = context.getBean(AService.class);
		BService bean2 = context.getBean(BService.class);
		System.out.println(bean);
		System.out.println(bean2);
	}

}
