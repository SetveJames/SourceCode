import org.springframework.context.support.ClassPathXmlApplicationContext;

import java.util.Map;
import java.util.Properties;

/**
 * 启动类
 */
public class Application {

	public static void main(String[] args) {
//		AnnotationConfigApplicationContext context = new AnnotationConfigApplicationContext(AppConfig.class);
//		AService bean = context.getBean(AService.class);
//		BService bean2 = context.getBean(BService.class);
//		System.out.println(bean);
//		System.out.println(bean2);

		//解析xml配置文件
		ClassPathXmlApplicationContext classPathXmlApplicationContext = new ClassPathXmlApplicationContext("classpath:aaa.xml");
//		ClassPathXmlApplicationContext classPathXmlApplicationContext = new ClassPathXmlApplicationContext("classpath:aaa/aaa.xml");
		//Object person1 = classPathXmlApplicationContext.getBean("person1");
		//System.out.println(person1);


//		Properties properties = System.getProperties();
//		Map<String, String> getenv = System.getenv();

		System.out.println("ok");

	}

}
